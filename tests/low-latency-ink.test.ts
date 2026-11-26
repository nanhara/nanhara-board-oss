import { describe, expect, it } from "vitest";
import { getStroke } from "perfect-freehand";
import {
  MAX_INK_PREVIEW_DIMENSION,
  MAX_INK_PREVIEW_PIXELS,
  getInkOutlineBounds,
  getInkPreviewScale,
  getInkPreviewSize,
  getInkPreviewStyle,
  getInkStrokeOptions,
  getInkStrokeOutline,
  withPredictedInkPoint,
  type InkPoint,
} from "../src/lib/low-latency-ink";

const points: InkPoint[] = [
  { x: 10, y: 20, pressure: 0.2 },
  { x: 18, y: 27, pressure: 0.55 },
  { x: 31, y: 25, pressure: 0.8 },
];

describe("low latency ink preview", () => {
  it("uses the exact selected color, opacity, and Excalidraw size", () => {
    expect(getInkPreviewStyle("#e03131", 40, 2, 1.5)).toEqual({
      color: "#e03131",
      opacity: 0.4,
      size: 12.75,
    });
  });

  it("mirrors Excalidraw 0.18.1 free-draw options", () => {
    const options = getInkStrokeOptions(8.5, true, false);

    expect(options).toMatchObject({
      simulatePressure: true,
      size: 8.5,
      thinning: 0.6,
      smoothing: 0.5,
      streamline: 0.5,
      last: false,
    });
    expect(options.easing?.(0.5)).toBeCloseTo(Math.SQRT1_2);
  });

  it("produces the same simulated-pressure outline as Excalidraw", () => {
    const style = getInkPreviewStyle("#1b1b1f", 100, 2, 1);
    const actual = getInkStrokeOutline(points, style, true, false);
    const expected = getStroke(
      points.map(({ x, y }) => [x, y]),
      {
        simulatePressure: true,
        size: 8.5,
        thinning: 0.6,
        smoothing: 0.5,
        streamline: 0.5,
        easing: (pressure) => Math.sin((pressure * Math.PI) / 2),
        last: false,
      },
    );

    expect(actual).toEqual(expected);
  });

  it("produces the same real-pressure outline as Excalidraw", () => {
    const style = getInkPreviewStyle("#1b1b1f", 100, 1, 1);
    const actual = getInkStrokeOutline(points, style, false, true);
    const expected = getStroke(
      points.map(({ x, y, pressure }) => [x, y, pressure]),
      {
        simulatePressure: false,
        size: 4.25,
        thinning: 0.6,
        smoothing: 0.5,
        streamline: 0.5,
        easing: (pressure) => Math.sin((pressure * Math.PI) / 2),
        last: true,
      },
    );

    expect(actual).toEqual(expected);
  });

  it("keeps only one raw point ahead of the committed frame stream", () => {
    const predicted = { x: 40, y: 31, pressure: 0.7 };
    expect(withPredictedInkPoint(points, predicted)).toEqual([
      ...points,
      predicted,
    ]);
    expect(withPredictedInkPoint(points, points.at(-1) ?? null)).toBe(points);
  });

  it("calculates a padded dirty rectangle for partial canvas clearing", () => {
    expect(
      getInkOutlineBounds(
        [
          [4, 8],
          [14, 3],
          [9, 18],
        ],
        2,
      ),
    ).toEqual({ left: 2, top: 1, right: 16, bottom: 20 });
  });

  it("caps a 4K preview backing store to keep the input path lightweight", () => {
    const scale = getInkPreviewScale(3840, 2160, 1);

    expect(3840 * scale).toBeLessThanOrEqual(MAX_INK_PREVIEW_DIMENSION);
    expect(3840 * 2160 * scale * scale).toBeLessThanOrEqual(
      MAX_INK_PREVIEW_PIXELS,
    );
  });

  it("keeps normal-density 1080p output at native resolution", () => {
    expect(getInkPreviewScale(1920, 1080, 1)).toBe(1);
  });

  it("does not clamp the visible size away from Excalidraw at low zoom", () => {
    expect(getInkPreviewSize(1, 0.1)).toBeCloseTo(0.425);
  });
});
