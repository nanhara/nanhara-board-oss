import { getStroke } from "perfect-freehand";
import type { StrokeOptions } from "perfect-freehand";

export const MAX_INK_PREVIEW_PIXELS = 4_000_000;
export const MAX_INK_PREVIEW_DIMENSION = 2_560;

const MIN_INK_PREVIEW_SCALE = 0.35;
const MAX_INK_PREVIEW_SCALE = 2;
const EXCALIDRAW_FREE_DRAW_SIZE = 4.25;

export type InkPoint = {
  x: number;
  y: number;
  pressure: number;
};

export type InkBounds = {
  left: number;
  top: number;
  right: number;
  bottom: number;
};

export type InkPreviewStyle = {
  color: string;
  opacity: number;
  size: number;
};

/** Keep the immediate preview visually identical to the selected Excalidraw
 * stroke. The color is deliberately passed through without substitution. */
export function getInkPreviewStyle(
  strokeColor: string,
  itemOpacity: number,
  strokeWidth: number,
  zoom: number,
): InkPreviewStyle {
  return {
    color: strokeColor,
    opacity: Math.min(1, Math.max(0, itemOpacity / 100)),
    size: getInkPreviewSize(strokeWidth, zoom),
  };
}

export function getInkPreviewScale(
  width: number,
  height: number,
  devicePixelRatio: number,
) {
  if (width <= 0 || height <= 0) return 1;

  const requestedScale = Math.min(
    Math.max(devicePixelRatio || 1, 1),
    MAX_INK_PREVIEW_SCALE,
  );
  const pixelLimitedScale = Math.sqrt(
    MAX_INK_PREVIEW_PIXELS / (width * height),
  );
  const dimensionLimitedScale =
    MAX_INK_PREVIEW_DIMENSION / Math.max(width, height);

  return Math.max(
    MIN_INK_PREVIEW_SCALE,
    Math.min(requestedScale, pixelLimitedScale, dimensionLimitedScale),
  );
}

/** Excalidraw 0.18.1 renders free-draw strokes at strokeWidth * 4.25. */
export function getInkPreviewSize(
  excalidrawStrokeWidth: number,
  zoom: number,
) {
  return Math.max(0, excalidrawStrokeWidth * EXCALIDRAW_FREE_DRAW_SIZE * zoom);
}

/** These options intentionally mirror Excalidraw 0.18.1's free-draw shape. */
export function getInkStrokeOptions(
  size: number,
  simulatePressure: boolean,
  last: boolean,
): StrokeOptions {
  return {
    simulatePressure,
    size,
    thinning: 0.6,
    smoothing: 0.5,
    streamline: 0.5,
    easing: (pressure) => Math.sin((pressure * Math.PI) / 2),
    last,
  };
}

export function getInkStrokeOutline(
  points: readonly InkPoint[],
  style: InkPreviewStyle,
  simulatePressure: boolean,
  last: boolean,
) {
  if (points.length === 0) return [];

  const inputPoints = simulatePressure
    ? points.map(({ x, y }) => [x, y])
    : points.map(({ x, y, pressure }) => [x, y, pressure]);

  return getStroke(
    inputPoints,
    getInkStrokeOptions(style.size, simulatePressure, last),
  );
}

/** Excalidraw throttles committed free-draw points to animation frames. Keep
 * only the newest raw point ahead of that stream so pressure simulation sees
 * the same point density instead of every high-frequency hardware sample. */
export function withPredictedInkPoint(
  committedPoints: readonly InkPoint[],
  predictedPoint: InkPoint | null,
) {
  if (!predictedPoint) return committedPoints;
  const lastPoint = committedPoints[committedPoints.length - 1];
  if (
    lastPoint &&
    lastPoint.x === predictedPoint.x &&
    lastPoint.y === predictedPoint.y
  ) {
    return committedPoints;
  }
  return [...committedPoints, predictedPoint];
}

export function getInkOutlineBounds(
  outline: readonly number[][],
  padding = 2,
): InkBounds | null {
  if (outline.length === 0) return null;

  let left = Number.POSITIVE_INFINITY;
  let top = Number.POSITIVE_INFINITY;
  let right = Number.NEGATIVE_INFINITY;
  let bottom = Number.NEGATIVE_INFINITY;

  for (const [x, y] of outline) {
    left = Math.min(left, x);
    top = Math.min(top, y);
    right = Math.max(right, x);
    bottom = Math.max(bottom, y);
  }

  return {
    left: left - padding,
    top: top - padding,
    right: right + padding,
    bottom: bottom + padding,
  };
}

/** Trace the same midpoint quadratic polygon used by Excalidraw's SVG path. */
export function traceInkOutline(
  context: CanvasRenderingContext2D,
  outline: readonly number[][],
) {
  if (outline.length < 2) return false;

  const [firstX, firstY] = outline[0];
  context.beginPath();
  context.moveTo(firstX, firstY);

  for (let index = 0; index < outline.length; index += 1) {
    const [controlX, controlY] = outline[index];
    const [nextX, nextY] = outline[(index + 1) % outline.length];
    context.quadraticCurveTo(
      controlX,
      controlY,
      (controlX + nextX) / 2,
      (controlY + nextY) / 2,
    );
  }

  context.lineTo(firstX, firstY);
  context.closePath();
  return true;
}
