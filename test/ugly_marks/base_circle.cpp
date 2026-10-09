void baseCircle(const GfxRenderer& renderer, const Circle role, const Box& box, const int padX, const int padY, const int width) {
  const CirclePoint* points = role == Circle::Word ? CIRCLE_WORD : role == Circle::Object ? CIRCLE_OBJECT : CIRCLE_ROW;
  const int count = role == Circle::Word ? CIRCLE_WORD_COUNT : role == Circle::Object ? CIRCLE_OBJECT_COUNT : CIRCLE_ROW_COUNT;
  const int centerX = (box.x0 + box.x1) / 2, centerY = (box.y0 + box.y1) / 2;
  const int radiusX = (box.x1 - box.x0) / 2 + padX, radiusY = (box.y1 - box.y0) / 2 + padY;
  int previousX = 0, previousY = 0;
  for (int index = 0; index < count; ++index) {
    const int offsetX = radiusX * points[index].x / 256;
    const int pointX = centerX + offsetX, pointY = centerY + radiusY * points[index].y / 256 - offsetX / 25;
    if (index > 0) stroke(renderer, previousX, previousY, pointX, pointY, width);
    previousX = pointX;
    previousY = pointY;
  }
}
