import {
  convertToExcalidrawElements,
  serializeAsJSON,
} from "@excalidraw/excalidraw";
import type {
  BinaryFileData,
  BinaryFiles,
  DataURL,
} from "@excalidraw/excalidraw/types";
import type { FileId } from "@excalidraw/excalidraw/element/types";
import { newBoardId, type BoardPage, type PageSourceKind } from "./board-model";
import { createThumbnail, inspectImage, type LoadedImage } from "./image-utils";

const MAX_SLIDE_WIDTH = 1600;
const MAX_SLIDE_HEIGHT = 1000;

const fitWithinCanvas = (width: number, height: number) => {
  const scale = Math.min(MAX_SLIDE_WIDTH / width, MAX_SLIDE_HEIGHT / height, 1);
  return {
    width: Math.round(width * scale),
    height: Math.round(height * scale),
  };
};

export const createImagePage = async ({
  dataUrl,
  name,
  kind,
  pageNumber,
}: {
  dataUrl: string;
  name: string;
  kind: Exclude<PageSourceKind, "blank">;
  pageNumber?: number;
}): Promise<BoardPage> => {
  const image: LoadedImage = await inspectImage(dataUrl);
  const size = fitWithinCanvas(image.width, image.height);
  const fileId = newBoardId() as FileId;
  const file: BinaryFileData = {
    id: fileId,
    dataURL: dataUrl as DataURL,
    mimeType: dataUrl.slice(5, dataUrl.indexOf(";")) as BinaryFileData["mimeType"],
    created: Date.now(),
  };
  const files: BinaryFiles = { [fileId]: file };
  const elements = convertToExcalidrawElements(
    [
      {
        type: "image",
        x: 0,
        y: 0,
        width: size.width,
        height: size.height,
        fileId,
        locked: true,
        opacity: 100,
        roughness: 0,
      },
    ],
    { regenerateIds: false },
  );
  const scene = serializeAsJSON(
    elements,
    {
      viewBackgroundColor: "#f4efe4",
      scrollX: 80,
      scrollY: 48,
    },
    files,
    "local",
  );

  return {
    id: newBoardId(),
    name: pageNumber ? `第 ${pageNumber} 页` : name,
    scene,
    thumbnail: await createThumbnail(image),
    source: {
      kind,
      fileName: name,
      pageNumber,
    },
  };
};

export const parseScene = (scene: string | null) => {
  if (!scene) {
    return {
      appState: { viewBackgroundColor: "#f4efe4" },
      elements: [],
      files: {},
    };
  }

  try {
    const parsed = JSON.parse(scene) as {
      appState?: Record<string, unknown>;
      elements?: unknown[];
      files?: Record<string, unknown>;
    };
    if (!Array.isArray(parsed.elements)) throw new Error("invalid scene");
    return {
      appState: parsed.appState ?? { viewBackgroundColor: "#f4efe4" },
      elements: parsed.elements,
      files: parsed.files ?? {},
    };
  } catch {
    return {
      appState: { viewBackgroundColor: "#f4efe4" },
      elements: [],
      files: {},
    };
  }
};
