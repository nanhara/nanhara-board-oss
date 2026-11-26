import { useEffect, useRef } from "react";
import type {
  AppState,
  ExcalidrawImperativeAPI,
} from "@excalidraw/excalidraw/types";
import type { OrderedExcalidrawElement } from "@excalidraw/excalidraw/element/types";
import {
  getInkOutlineBounds,
  getInkPreviewScale,
  getInkPreviewStyle,
  getInkStrokeOutline,
  traceInkOutline,
  withPredictedInkPoint,
  type InkBounds,
  type InkPoint,
  type InkPreviewStyle,
} from "../lib/low-latency-ink";

type ActiveStroke = {
  pointerId: number;
  originLeft: number;
  originTop: number;
  existingElementIds: Set<string>;
  elementId: string | null;
  committedPoints: InkPoint[];
  predictedPoint: InkPoint | null;
  simulatePressure: boolean;
  style: InkPreviewStyle;
  renderedBounds: InkBounds | null;
  isFinal: boolean;
};

type SceneChangedHandler = (
  elements: readonly OrderedExcalidrawElement[],
  appState: AppState,
) => void;

const FINAL_STROKE_FALLBACK_MS = 160;

function eventPoint(
  event: PointerEvent,
  originLeft: number,
  originTop: number,
): InkPoint {
  return {
    x: event.clientX - originLeft,
    y: event.clientY - originTop,
    pressure: event.pressure,
  };
}

function clearBounds(
  context: CanvasRenderingContext2D,
  bounds: InkBounds | null,
) {
  if (!bounds) return;
  context.clearRect(
    bounds.left,
    bounds.top,
    bounds.right - bounds.left,
    bounds.bottom - bounds.top,
  );
}

export function LowLatencyInkCanvas({
  hostRef,
  apiRef,
  sceneChangedRef,
}: {
  hostRef: React.RefObject<HTMLDivElement>;
  apiRef: React.MutableRefObject<ExcalidrawImperativeAPI | null>;
  sceneChangedRef: React.MutableRefObject<SceneChangedHandler | null>;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const host = hostRef.current;
    const canvas = canvasRef.current;
    if (!host || !canvas) return;

    const context = canvas.getContext("2d", {
      alpha: true,
      desynchronized: true,
    });
    if (!context) return;

    let canvasScale = 1;
    let activeStroke: ActiveStroke | null = null;
    let completedStroke: ActiveStroke | null = null;
    let renderFrame: number | null = null;
    let clearTimer: number | null = null;
    let clearFrame: number | null = null;

    const cancelScheduledRender = () => {
      if (renderFrame !== null) window.cancelAnimationFrame(renderFrame);
      renderFrame = null;
    };

    const cancelScheduledClear = () => {
      if (clearTimer !== null) window.clearTimeout(clearTimer);
      if (clearFrame !== null) window.cancelAnimationFrame(clearFrame);
      clearTimer = null;
      clearFrame = null;
    };

    const clearStroke = (stroke: ActiveStroke | null) => {
      if (!stroke) {
        context.clearRect(0, 0, canvas.width / canvasScale, canvas.height / canvasScale);
        return;
      }
      clearBounds(context, stroke.renderedBounds);
      stroke.renderedBounds = null;
    };

    const renderStroke = (stroke: ActiveStroke) => {
      clearStroke(stroke);
      const points = withPredictedInkPoint(
        stroke.committedPoints,
        stroke.predictedPoint,
      );
      const outline = getInkStrokeOutline(
        points,
        stroke.style,
        stroke.simulatePressure,
        stroke.isFinal,
      );
      if (!traceInkOutline(context, outline)) return;

      context.fillStyle = stroke.style.color;
      context.globalAlpha = 1;
      canvas.style.opacity = String(stroke.style.opacity);
      context.fill();
      stroke.renderedBounds = getInkOutlineBounds(outline);
    };

    const scheduleRender = (stroke: ActiveStroke) => {
      if (renderFrame !== null) return;
      renderFrame = window.requestAnimationFrame(() => {
        renderFrame = null;
        if (activeStroke === stroke || completedStroke === stroke) {
          renderStroke(stroke);
        }
      });
    };

    const resizeCanvas = () => {
      cancelScheduledRender();
      cancelScheduledClear();
      const bounds = host.getBoundingClientRect();
      canvasScale = getInkPreviewScale(
        bounds.width,
        bounds.height,
        window.devicePixelRatio,
      );
      canvas.width = Math.max(1, Math.round(bounds.width * canvasScale));
      canvas.height = Math.max(1, Math.round(bounds.height * canvasScale));
      context.setTransform(canvasScale, 0, 0, canvasScale, 0, 0);
      activeStroke = null;
      completedStroke = null;
    };

    const syncCommittedStroke = (
      stroke: ActiveStroke,
      elements: readonly OrderedExcalidrawElement[],
      appState: AppState,
    ) => {
      let element = stroke.elementId
        ? elements.find((candidate) => candidate.id === stroke.elementId)
        : null;

      if (!element) {
        for (let index = elements.length - 1; index >= 0; index -= 1) {
          const candidate = elements[index];
          if (
            candidate.type === "freedraw" &&
            !stroke.existingElementIds.has(candidate.id)
          ) {
            element = candidate;
            stroke.elementId = candidate.id;
            break;
          }
        }
      }

      if (!element || element.type !== "freedraw") return false;

      const zoom = appState.zoom.value;
      const elementLeft =
        (element.x + appState.scrollX) * zoom +
        appState.offsetLeft -
        stroke.originLeft;
      const elementTop =
        (element.y + appState.scrollY) * zoom +
        appState.offsetTop -
        stroke.originTop;

      stroke.committedPoints = element.points.map(([x, y], index) => ({
        x: elementLeft + x * zoom,
        y: elementTop + y * zoom,
        pressure: element.simulatePressure
          ? 0.5
          : (element.pressures[index] ?? 0.5),
      }));
      stroke.simulatePressure = element.simulatePressure;
      stroke.style = getInkPreviewStyle(
        element.strokeColor,
        element.opacity,
        element.strokeWidth,
        zoom,
      );
      stroke.isFinal = Boolean(element.lastCommittedPoint);

      const lastCommitted = stroke.committedPoints.at(-1);
      if (
        lastCommitted &&
        stroke.predictedPoint &&
        lastCommitted.x === stroke.predictedPoint.x &&
        lastCommitted.y === stroke.predictedPoint.y
      ) {
        stroke.predictedPoint = null;
      }
      return true;
    };

    const onSceneChanged: SceneChangedHandler = (elements, appState) => {
      const stroke = activeStroke ?? completedStroke;
      if (!stroke || !syncCommittedStroke(stroke, elements, appState)) return;

      if (activeStroke === stroke) {
        scheduleRender(stroke);
        return;
      }

      // Pointer-up has now reached Excalidraw. Remove prediction and render one
      // exact final frame before its vector canvas takes over.
      cancelScheduledRender();
      stroke.predictedPoint = null;
      stroke.isFinal = true;
      renderStroke(stroke);
      if (clearTimer !== null) {
        window.clearTimeout(clearTimer);
        clearTimer = null;
      }
      if (clearFrame === null) {
        clearFrame = window.requestAnimationFrame(() => {
          if (completedStroke === stroke) {
            clearStroke(stroke);
            completedStroke = null;
          }
          clearFrame = null;
        });
      }
    };

    sceneChangedRef.current = onSceneChanged;

    const onPointerDown = (event: PointerEvent) => {
      if (
        event.button !== 0 ||
        !event.isPrimary ||
        !(event.target instanceof HTMLCanvasElement)
      ) {
        return;
      }

      const api = apiRef.current;
      const appState = api?.getAppState();
      if (!api || !appState || appState.activeTool.type !== "freedraw") return;

      cancelScheduledRender();
      cancelScheduledClear();
      clearStroke(null);
      completedStroke = null;

      const bounds = host.getBoundingClientRect();
      const point = eventPoint(event, bounds.left, bounds.top);
      activeStroke = {
        pointerId: event.pointerId,
        originLeft: bounds.left,
        originTop: bounds.top,
        existingElementIds: new Set(
          api.getSceneElements().map((element) => element.id),
        ),
        elementId: null,
        committedPoints: [point],
        predictedPoint: null,
        simulatePressure: event.pressure === 0.5,
        style: getInkPreviewStyle(
          appState.currentItemStrokeColor,
          appState.currentItemOpacity,
          appState.currentItemStrokeWidth,
          appState.zoom.value,
        ),
        renderedBounds: null,
        isFinal: false,
      };
      renderStroke(activeStroke);
    };

    const onPointerMove = (event: PointerEvent) => {
      const stroke = activeStroke;
      if (!stroke || event.pointerId !== stroke.pointerId) return;
      stroke.predictedPoint = eventPoint(
        event,
        stroke.originLeft,
        stroke.originTop,
      );
      scheduleRender(stroke);
    };

    const finishStroke = (event: PointerEvent) => {
      const stroke = activeStroke;
      if (!stroke || event.pointerId !== stroke.pointerId) return;

      stroke.predictedPoint = eventPoint(
        event,
        stroke.originLeft,
        stroke.originTop,
      );
      stroke.isFinal = true;
      activeStroke = null;
      completedStroke = stroke;
      scheduleRender(stroke);

      // Excalidraw normally reports the finalized vector scene immediately
      // after pointer-up. This is only a fail-safe if that callback is missed.
      clearTimer = window.setTimeout(() => {
        if (completedStroke === stroke) {
          clearStroke(stroke);
          completedStroke = null;
        }
        clearTimer = null;
      }, FINAL_STROKE_FALLBACK_MS);
    };

    const onPointerCancel = (event: PointerEvent) => {
      const stroke = activeStroke;
      if (!stroke || event.pointerId !== stroke.pointerId) return;
      cancelScheduledRender();
      activeStroke = null;
      clearStroke(stroke);
    };

    const onPointerMoveEvent: EventListener = (event) => {
      if (event instanceof PointerEvent) onPointerMove(event);
    };

    const resizeObserver = new ResizeObserver(resizeCanvas);
    resizeObserver.observe(host);
    resizeCanvas();

    host.addEventListener("pointerdown", onPointerDown, true);
    window.addEventListener("pointermove", onPointerMoveEvent, true);
    const hasRawPointerUpdates = "onpointerrawupdate" in window;
    if (hasRawPointerUpdates) {
      window.addEventListener("pointerrawupdate", onPointerMoveEvent, true);
    }
    window.addEventListener("pointerup", finishStroke, true);
    window.addEventListener("pointercancel", onPointerCancel, true);

    return () => {
      cancelScheduledRender();
      cancelScheduledClear();
      if (sceneChangedRef.current === onSceneChanged) {
        sceneChangedRef.current = null;
      }
      resizeObserver.disconnect();
      host.removeEventListener("pointerdown", onPointerDown, true);
      window.removeEventListener("pointermove", onPointerMoveEvent, true);
      if (hasRawPointerUpdates) {
        window.removeEventListener("pointerrawupdate", onPointerMoveEvent, true);
      }
      window.removeEventListener("pointerup", finishStroke, true);
      window.removeEventListener("pointercancel", onPointerCancel, true);
    };
  }, [apiRef, hostRef, sceneChangedRef]);

  return <canvas ref={canvasRef} className="low-latency-ink" aria-hidden="true" />;
}
