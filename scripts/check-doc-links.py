#!/usr/bin/env python3
"""Checks relative links (incl. heading anchors) in all tracked Markdown files. Usage: scripts/check-doc-links.py [repo-root]. Exit 1 on broken links."""
import os, re, subprocess, sys
root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
files = subprocess.check_output(['git', 'ls-files', '*.md', '--others', '--cached', '--exclude-standard'], cwd=root, text=True).split()
files = sorted(set(f for f in files if os.path.exists(os.path.join(root, f))))

def slug(h):
    h = h.strip().lower()
    h = re.sub(r'[`*_]', '', h)
    h = re.sub(r'[^\w\- ]', '', h)
    return h.replace(' ', '-')

anchors = {}
def get_anchors(path):
    if path not in anchors:
        a = set()
        in_code = False
        for line in open(path, encoding='utf-8'):
            if line.startswith('```'):
                in_code = not in_code
            if not in_code:
                m = re.match(r'#+\s+(.*)', line)
                if m:
                    a.add(slug(m.group(1)))
        anchors[path] = a
    return anchors[path]

bad = 0
for f in files:
    p = os.path.join(root, f)
    text = open(p, encoding='utf-8').read()
    text = re.sub(r'```.*?```', '', text, flags=re.S)
    text = re.sub(r'`[^`\n]*`', '', text)
    for m in re.finditer(r'\[[^\]]*\]\(([^)\s]+)\)', text):
        t = m.group(1)
        if re.match(r'[a-z]+:', t):
            continue
        path, _, frag = t.partition('#')
        target = os.path.normpath(os.path.join(os.path.dirname(p), path)) if path else p
        if not os.path.exists(target):
            print(f'{f}: missing {t}')
            bad += 1
        elif frag and target.endswith('.md') and frag not in get_anchors(target):
            print(f'{f}: missing anchor {t}')
            bad += 1
print('broken:', bad, 'files:', len(files))
sys.exit(1 if bad else 0)
