"""Select this experiment's mod without changing other package selections."""
import argparse
from pathlib import Path
import re
import tomllib

PACKAGE = "parappa.accessibility.timing"


def configure(path, enabled, early, late, offset):
    original = path.read_text() if path.exists() else "format_version = 2\n"
    data = tomllib.loads(original)
    if data.get("format_version") != 2:
        raise ValueError("Timing tool requires mod state format 2")
    blocks = re.split(r"(?m)(?=^\[\[(?:package|feature)\]\]\s*$)", original)
    retained = []
    for block in blocks:
        item = tomllib.loads(block)
        owned_package = any(p.get("id") == PACKAGE for p in item.get("package", []))
        owned_feature = any(f.get("package_id") == PACKAGE for f in item.get("feature", []))
        if not (owned_package or owned_feature):
            retained.append(block)
    selected = f'''
[[package]]
id = "{PACKAGE}"
version = "0.1.0"

[[feature]]
package_id = "{PACKAGE}"
id = "judgement"
enabled = {str(enabled).lower()}
[feature.values]
early_ms = "{early}"
late_ms = "{late}"
offset_ms = "{offset}"
'''
    result = "".join(retained).rstrip() + "\n" + selected
    tomllib.loads(result)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(result)


def bounded(value, low, high):
    value = int(value)
    if not low <= value <= high or value % 5:
        raise argparse.ArgumentTypeError(f"Use {low}..{high} in steps of 5")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--stock", action="store_true")
    parser.add_argument("--early", type=lambda v: bounded(v, 0, 60), default=10)
    parser.add_argument("--late", type=lambda v: bounded(v, 0, 60), default=10)
    parser.add_argument("--offset", type=lambda v: bounded(v, -300, 300), default=0)
    args = parser.parse_args()
    configure(args.state, not args.stock, args.early, args.late, args.offset)


if __name__ == "__main__":
    main()
