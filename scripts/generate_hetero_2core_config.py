#!/usr/bin/env python3

import argparse
import json
from copy import deepcopy
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate a true hetero 2-core ChampSim config with per-core L2 prefetchers.")
    parser.add_argument("--base-config", default="configs/orig_2core.json")
    parser.add_argument("--core0-prefetcher", required=True)
    parser.add_argument("--core1-prefetcher", required=True)
    parser.add_argument("--executable-name", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    base_path = Path(args.base_config)
    config = json.loads(base_path.read_text())

    if config.get("num_cores") != 2:
        raise ValueError("base config must describe a 2-core system")

    base_cpu = deepcopy(config["ooo_cpu"][0])
    core0 = deepcopy(base_cpu)
    core1 = deepcopy(base_cpu)
    core0["L2C"] = {"prefetcher": args.core0_prefetcher}
    core1["L2C"] = {"prefetcher": args.core1_prefetcher}

    config["ooo_cpu"] = [core0, core1]
    config["num_cores"] = 2
    config["executable_name"] = args.executable_name

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(config, indent=2) + "\n")


if __name__ == "__main__":
    main()
