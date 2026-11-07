#!/usr/bin/env python3
"""Require actual execution and no skipped, failed, absent or duplicate cases."""
import sys
import xml.etree.ElementTree as ET
from collections import Counter
path,*expected=sys.argv[1:]
cases=ET.parse(path).findall('.//testcase')
names=Counter(c.get('name') for c in cases)
if names!=Counter(expected):raise SystemExit(f'Unexpected/absent/duplicate tests: {names}; expected {expected}')
for c in cases:
    if c.get('status') in {'notrun','disabled'} or any(c.find(k) is not None for k in ['skipped','failure','error']):
        raise SystemExit(f'Test did not pass with actual execution: {c.get("name")}')
print('All expected cases actually executed and passed:',', '.join(expected))
