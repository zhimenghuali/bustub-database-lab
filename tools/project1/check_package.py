"""校验提交压缩包的六文件清单与源码哈希，并覆盖到干净基线。"""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile

source = Path(sys.argv[1])
destination = Path(sys.argv[2])
evidence = Path(sys.argv[3])
expected = {
    "src/include/container/hash/extendible_hash_table.h",
    "src/container/hash/extendible_hash_table.cpp",
    "src/include/buffer/lru_k_replacer.h",
    "src/buffer/lru_k_replacer.cpp",
    "src/include/buffer/buffer_pool_manager_instance.h",
    "src/buffer/buffer_pool_manager_instance.cpp",
}
archive_path = source / "project1-submission.zip"
manifest = {}
with zipfile.ZipFile(archive_path) as archive:
    assert len(archive.namelist()) == 6 and set(archive.namelist()) == expected, archive.namelist()
    for name in sorted(expected):
        content = archive.read(name)
        assert content == (source / name).read_bytes(), name
        (destination / name).write_bytes(content)
        manifest[name] = hashlib.sha256(content).hexdigest()
shutil.copy2(str(archive_path), str(evidence / archive_path.name))
(evidence / "package-manifest.json").write_text(json.dumps({
    "archive_sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest(),
    "files": manifest,
}, indent=2), encoding="utf-8")
print("提交包六文件与待测源码一致，已覆盖到干净基线。")
