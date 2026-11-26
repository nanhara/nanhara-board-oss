import { bench, describe } from "vitest";
import {
  getInkPreviewStyle,
  getInkStrokeOutline,
  withPredictedInkPoint,
  type InkPoint,
} from "../src/lib/low-latency-ink";

const style = getInkPreviewStyle("#1b1b1f", 100, 2, 1);

function makeStroke(pointCount: number): InkPoint[] {
  return Array.from({ length: pointCount }, (_, index) => ({
    x: index * 0.8,
    y: Math.sin(index / 10) * 20,
    pressure: 0.5,
  }));
}

describe("ink outline generation", () => {
  const normalStroke = makeStroke(1_000);
  const longStroke = makeStroke(5_000);

  bench("1,000 committed points plus one prediction", () => {
    const points = withPredictedInkPoint(normalStroke, {
      x: 801,
      y: 0,
      pressure: 0.5,
    });
    getInkStrokeOutline(points, style, true, false);
  });

  bench("5,000 committed points plus one prediction", () => {
    const points = withPredictedInkPoint(longStroke, {
      x: 4_001,
      y: 0,
      pressure: 0.5,
    });
    getInkStrokeOutline(points, style, true, false);
  });
});
