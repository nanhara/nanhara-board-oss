import { useCallback, useEffect, useMemo, useRef } from "react";
import {
  CaptureUpdateAction,
  Excalidraw,
  serializeAsJSON,
} from "@excalidraw/excalidraw";
import type {
  AppState,
  BinaryFiles,
  ExcalidrawImperativeAPI,
  ExcalidrawInitialDataState,
} from "@excalidraw/excalidraw/types";
import type { OrderedExcalidrawElement } from "@excalidraw/excalidraw/element/types";
import { Trash2 } from "lucide-react";
import { parseScene } from "../lib/scene-factory";
import { LowLatencyInkCanvas } from "./LowLatencyInkCanvas";

type PendingScene = {
  elements: readonly OrderedExcalidrawElement[];
  appState: AppState;
  files: BinaryFiles;
};

const SCENE_COMMIT_DELAY_MS = 320;

export default function BoardCanvas({
  pageId,
  scene,
  onSceneChange,
}: {
  pageId: string;
  scene: string | null;
  onSceneChange: (pageId: string, scene: string) => void;
}) {
  const initialData = useMemo(
    () => parseScene(scene) as ExcalidrawInitialDataState,
    [scene],
  );
  const pending = useRef<PendingScene | null>(null);
  const timer = useRef<number | null>(null);
  const hostRef = useRef<HTMLDivElement>(null);
  const apiRef = useRef<ExcalidrawImperativeAPI | null>(null);
  const inkSceneChangedRef = useRef<
    | ((
        elements: readonly OrderedExcalidrawElement[],
        appState: AppState,
      ) => void)
    | null
  >(null);

  const flush = useCallback(() => {
    if (timer.current !== null) {
      window.clearTimeout(timer.current);
      timer.current = null;
    }
    if (!pending.current) return;
    const next = pending.current;
    pending.current = null;
    onSceneChange(
      pageId,
      serializeAsJSON(next.elements, next.appState, next.files, "local"),
    );
  }, [onSceneChange, pageId]);

  useEffect(() => flush, [flush]);

  const clearPage = useCallback(() => {
    const api = apiRef.current;
    if (!api) return;
    if (api.getSceneElements().length === 0) {
      api.setToast({ message: "本页已经是空白页", duration: 1_800 });
      return;
    }

    api.updateScene({
      elements: [],
      captureUpdate: CaptureUpdateAction.IMMEDIATELY,
    });
    api.setToast({
      message: "本页已清空，可用 Ctrl/⌘+Z 撤销",
      duration: 2_600,
    });
  }, []);

  const renderTopRightUI = useCallback(
    (isMobile: boolean) => (
      <button
        type="button"
        className="board-clear-button"
        onPointerDown={(event) => event.stopPropagation()}
        onClick={clearPage}
        aria-label="清空本页"
        title="清空本页（可撤销）"
      >
        <Trash2 size={17} strokeWidth={1.8} aria-hidden="true" />
        {isMobile ? null : <span>清空本页</span>}
      </button>
    ),
    [clearPage],
  );

  return (
    <div ref={hostRef} className="board-canvas" data-page-id={pageId}>
      <Excalidraw
        excalidrawAPI={(api) => {
          apiRef.current = api;
        }}
        initialData={initialData}
        langCode="zh-CN"
        theme="light"
        name="南荒白板"
        handleKeyboardGlobally
        autoFocus
        renderTopRightUI={renderTopRightUI}
        UIOptions={{
          canvasActions: {
            changeViewBackgroundColor: false,
            clearCanvas: false,
            export: false,
            loadScene: false,
            saveAsImage: false,
            saveToActiveFile: false,
            toggleTheme: false,
          },
          tools: { image: true },
        }}
        onChange={(elements, appState, files) => {
          inkSceneChangedRef.current?.(elements, appState);
          pending.current = { elements, appState, files };
          if (timer.current !== null) window.clearTimeout(timer.current);
          timer.current = window.setTimeout(flush, SCENE_COMMIT_DELAY_MS);
        }}
      />
      <LowLatencyInkCanvas
        hostRef={hostRef}
        apiRef={apiRef}
        sceneChangedRef={inkSceneChangedRef}
      />
    </div>
  );
}
