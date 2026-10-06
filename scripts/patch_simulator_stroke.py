"""Give the simulator's input script a stroke with turns, for the marks drawn on the touch screen.

  STROKE:<ms>,x1,y1,x2,y2[,x3,y3...]
    One finger down at the first point, through each point in turn (10 samples a segment, evenly in
    time), up at the last, the whole in <ms>. Points in logical px, or 0..1 like TAP and SWIPE.
    A row struck out and back is STROKE:400,100,300,380,304,110,310.

Same contract as patch_simulator_grayscale.py: exactly one match, skip when applied, fail loudly when the
upstream text moved.
"""
from pathlib import Path

Import("env")
path = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src" / "HalGPIO.cpp"
OLD = '      } else if ((key == "TAP" || key == "SWIPE") &&\n'
NEW = '''      } else if (key == "STROKE" && secondColon != std::string::npos) {
        // Tenor: a stroke through several points (scripts/patch_simulator_stroke.py).
        std::vector<float> v;
        const std::string detail = item.substr(secondColon + 1);
        for (size_t at = 0;;) {
          const size_t comma = detail.find(',', at);
          v.push_back(std::strtof(detail.substr(at, comma == std::string::npos ? std::string::npos : comma - at).c_str(), nullptr));
          if (comma == std::string::npos) break;
          at = comma + 1;
        }
        if (v.size() >= 5 && v.size() % 2 == 1) {
          const auto norm = [](float value, int extent) {
            return value >= 0.0f && value <= 1.0f ? value : clamp01(value / static_cast<float>(std::max(1, extent - 1)));
          };
          const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
          const unsigned long duration = static_cast<unsigned long>(v[0]);
          const size_t points = (v.size() - 1) / 2, steps = (points - 1) * 10;
          for (size_t k = 0; k <= steps; ++k) {
            const size_t seg = k == steps ? points - 2 : k / 10;
            const float f = k == steps ? 1.0f : static_cast<float>(k % 10) / 10.0f;
            const float x = v[1 + 2 * seg] + (v[3 + 2 * seg] - v[1 + 2 * seg]) * f;
            const float y = v[2 + 2 * seg] + (v[4 + 2 * seg] - v[2 + 2 * seg]) * f;
            syntheticEvents.push_back({atMs + duration * k / steps, k ? SyntheticAction::TouchMove : SyntheticAction::TouchDown,
                                       -1, norm(x, w), norm(y, h)});
          }
          syntheticEvents.push_back({atMs + duration + 1, SyntheticAction::TouchUp, -1, norm(v[v.size() - 2], w),
                                     norm(v[v.size() - 1], h)});
        }
''' + OLD
if path.exists():
    source = path.read_text()
    if NEW not in source:
        if source.count(OLD) != 1:
            raise RuntimeError("Simulator input script source changed; review patch before building")
        path.write_text(source.replace(OLD, NEW, 1))
        print("Applied simulator stroke patch to " + path.name)
