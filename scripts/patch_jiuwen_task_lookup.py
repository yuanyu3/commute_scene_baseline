#!/usr/bin/env python3
from pathlib import Path
import sys
p = Path(sys.argv[1] if len(sys.argv) > 1 else "/mnt/d/bbpjiuwen/src/controller/task/Task.cpp")
text = p.read_text(encoding="utf-8")
old = (
    "        if (iter != callId2TaskOutput.end()) {\n"
    "            LOG(ERROR) << \"[React-Task] can't find ]\" << subTask.callId << \"'s output\";\n"
    "            continue;\n"
    "        }"
)
new = (
    "        if (iter == callId2TaskOutput.end()) {\n"
    "            LOG(ERROR) << \"[React-Task] can't find \" << subTask.callId << \"'s output\";\n"
    "            continue;\n"
    "        }"
)
if old not in text:
    if new in text:
        print("already patched:", p)
        raise SystemExit(0)
    raise SystemExit(f"pattern not found in {p}")
p.write_text(text.replace(old, new, 1), encoding="utf-8")
print("patched:", p)
