from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()

def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

output = '#include <GfxRenderer.h>\nnamespace ugly {\n'
output += method('void dot(') + '\n' + method('void stroke(const GfxRenderer& r, int x0, int y0, const int x1') + '\n'
output += method('void penStroke(') + '\n}\n'
output += 'void GfxRenderer::drawLine(int x0,int y0,int x1,int y1,bool black) const {\n'
output += ' if (black) ugly::penStroke(*this,x0,y0,x1,y1,1);\n}\n'
Path(sys.argv[2]).write_text(output)
