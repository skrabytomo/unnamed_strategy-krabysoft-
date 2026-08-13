#!/usr/bin/env python3
"""Fix Camera2D.h - properly add m_contentScale member variable."""

def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

path = 'src/renderer/Camera2D.h'
content = read_file(path)

# Check for the actual member variable declaration
if 'float m_contentScale' not in content:
    old = '    int   m_w    = 1280;\n    int   m_h    = 720;\n};'
    new = '    int   m_w    = 1280;\n    int   m_h    = 720;\n    float m_contentScale = 1.0f;\n};'
    if old in content:
        content = content.replace(old, new)
        write_file(path, content)
        print(f'[FIXED] {path} - added m_contentScale member')
    else:
        print(f'[ERROR] Could not find insertion point')
else:
    print(f'[OK] {path} - m_contentScale already present')

print('Build: cmake --build build -j4')