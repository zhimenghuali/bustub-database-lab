"""核验测试实际执行数；退出码或 XML 缺失均不能视为通过。"""
import csv
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

root = Path(sys.argv[1])
mode = sys.argv[2]
expected = {
    "extendible_hash_table_test": 12,
    "lru_k_replacer_test": 4,
    "buffer_pool_manager_instance_test": 13,
}
if mode != "package":
    expected.update({
        "extendible_hash_table_regression_test": 3,
        "lru_k_replacer_regression_test": 6,
        "buffer_pool_manager_regression_test": 4,
    })
with (root / "exit-codes.tsv").open() as stream:
    steps = list(csv.DictReader(stream, delimiter="\t"))
assert all(int(step["exit_code"]) == 0 for step in steps), steps
executed = {step["step"] for step in steps}
assert {"configure", "build", *expected}.issubset(executed), executed
results = []
for target, count in expected.items():
    xml = ET.parse(str(root / (target + ".xml"))).getroot()
    values = {key: int(xml.get(key, "0")) for key in ("tests", "failures", "errors", "disabled")}
    cases = xml.findall(".//testcase")
    assert values == {"tests": count, "failures": 0, "errors": 0, "disabled": 0}, (target, values)
    assert len(cases) == count and all(case.get("status") == "run" for case in cases), target
    assert all(case.find("skipped") is None and case.get("result") != "skipped" for case in cases), target
    results.append(dict(target=target, **values))
summary = {"mode": mode, "passed": True, "tests": sum(expected.values()), "results": results}
(root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
