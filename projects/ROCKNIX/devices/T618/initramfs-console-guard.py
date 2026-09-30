#!/usr/bin/env python3
"""Replace the fatal console redirection in ROCKNIX's initramfs /init with a guarded one."""
import sys
p = sys.argv[1]; t = open(p).read()
old = 'exec 1>/dev/console\nexec 2>/dev/null\n'
new = ('if ( : >/dev/console ) 2>/dev/null; then\n'
       '  exec 1>/dev/console\n'
       'else\n'
       '  exec 1>/dev/kmsg\n'
       '  echo "t618: /dev/console not openable, init output -> kmsg"\n'
       'fi\n'
       'exec 2>/dev/null\n')
if new in t:
    sys.exit(0)
if old not in t:
    sys.exit('console redirection block not found in ' + p)
open(p, 'w').write(t.replace(old, new, 1))
