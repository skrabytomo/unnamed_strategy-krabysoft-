#!/usr/bin/env python3
def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

path = 'src/ai/Pathfinder.cpp'
content = read_file(path)

old = '        auto [f, current] = open.top(); open.pop();'
new = '        auto [f, g, current] = open.top(); open.pop();'
content = content.replace(old, new)

old = '    open.push({ HexGrid::distance(start, goal), 0, start });'
new = '    open.push(Node{HexGrid::distance(start, goal), 0, start });'
content = content.replace(old, new)

old = '                open.push({ tentG + h, tentG, nb });'
new = '                open.push(Node{tentG + h, tentG, nb });'
content = content.replace(old, new)

write_file(path, content)
print('[OK] src/ai/Pathfinder.cpp')
print('Build: cmake --build build -j4')