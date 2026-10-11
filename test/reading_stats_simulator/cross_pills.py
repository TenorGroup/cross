"""Find tenor/cross path pills in a screenshot: a closed ring of ink, 38 to 64 px high and 40 px wide or more, in the
foot of the screen (y >= TOP), with a big empty inside. The hand never draws one there (its rings are bigger or sit
higher). find(im) -> [(x0, y0, x1, y1)].  cross_pills.py <png...>: one line per image, exit 1 when any image has one."""
import sys
from collections import deque
from PIL import Image

TOP = 680


def find(im, top=TOP):
    im = im.convert('L')
    w, h = im.size
    px = im.load()
    ink = [[px[x, y] < 128 for x in range(w)] for y in range(top, h)]
    rows = h - top
    seen = [[False] * w for _ in range(rows)]
    found = []
    for y in range(rows):
        for x in range(w):
            if not ink[y][x] or seen[y][x]:
                continue
            comp, queue = [], deque([(x, y)])
            seen[y][x] = True
            while queue:
                cx, cy = queue.popleft()
                comp.append((cx, cy))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < w and 0 <= ny < rows and ink[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            queue.append((nx, ny))
            x0, x1 = min(c[0] for c in comp), max(c[0] for c in comp)
            y0, y1 = min(c[1] for c in comp), max(c[1] for c in comp)
            if not (38 <= y1 - y0 + 1 <= 64 and x1 - x0 + 1 >= 40):
                continue
            # the white that the outside reaches from the box border; what is left inside is enclosed
            outside = set()
            queue = deque()
            for bx in range(x0 - 1, x1 + 2):
                for by in (y0 - 1, y1 + 1):
                    queue.append((bx, by)); outside.add((bx, by))
            for by in range(y0 - 1, y1 + 2):
                for bx in (x0 - 1, x1 + 1):
                    queue.append((bx, by)); outside.add((bx, by))
            while queue:
                cx, cy = queue.popleft()
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if x0 - 1 <= nx <= x1 + 1 and y0 - 1 <= ny <= y1 + 1 and (nx, ny) not in outside and not (
                            0 <= ny < rows and 0 <= nx < w and ink[ny][nx]):
                        outside.add((nx, ny)); queue.append((nx, ny))
            inside = (x1 - x0 + 3) * (y1 - y0 + 3) - len(outside) - len(comp)
            if inside >= 0.5 * (x1 - x0 - 6) * (y1 - y0 - 6):
                found.append((x0, y0 + top, x1, y1 + top))
    return found


if __name__ == '__main__':
    bad = 0
    for path in sys.argv[1:]:
        boxes = find(Image.open(path))
        bad |= bool(boxes)
        print(path, boxes or 'none')
    sys.exit(bad)
