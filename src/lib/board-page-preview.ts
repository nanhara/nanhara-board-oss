import type {
  BinaryFiles,
  ExcalidrawInitialDataState,
} from "@excalidraw/excalidraw/types";
import type {
  ExcalidrawElement,
  NonDeleted,
} from "@excalidraw/excalidraw/element/types";
import type { BoardPage } from "./board-model";

export const renderBoardPagePreview = async (
  page: BoardPage,
): Promise<string | null> => {
  if (!page.scene) return null;

  const scene = JSON.parse(page.scene) as ExcalidrawInitialDataState;
  const elements = (scene.elements ?? []).filter(
    (element) => !element.isDeleted,
  ) as readonly NonDeleted<ExcalidrawElement>[];
  if (elements.length === 0) return null;

  const { exportToBlob } = await import("@excalidraw/excalidraw");
  const blob = await exportToBlob({
    elements,
    files: (scene.files ?? {}) as BinaryFiles,
    appState: {
      ...(scene.appState ?? {}),
      exportBackground: true,
      viewBackgroundColor:
        scene.appState?.viewBackgroundColor ?? "#f4efe4",
    },
    mimeType: "image/png",
    maxWidthOrHeight: 2_560,
    exportPadding: 72,
  });

  return URL.createObjectURL(blob);
};
