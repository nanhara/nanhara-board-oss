import pdfWorkerUrl from "pdfjs-dist/build/pdf.worker.min.mjs?url";
import { createImagePage } from "./scene-factory";
import type { BoardPage, PageSourceKind } from "./board-model";

const RENDER_WIDTH = 1920;

export const importPdfBytes = async ({
  bytes,
  fileName,
  kind = "pdf",
  onProgress,
}: {
  bytes: Uint8Array;
  fileName: string;
  kind?: Extract<PageSourceKind, "pdf" | "powerpoint" | "slidev">;
  onProgress?: (current: number, total: number) => void;
}): Promise<BoardPage[]> => {
  const pdfjs = await import("pdfjs-dist");
  pdfjs.GlobalWorkerOptions.workerSrc = pdfWorkerUrl;
  const loadingTask = pdfjs.getDocument({ data: bytes.slice() });
  const pdfDocument = await loadingTask.promise;
  const pages: BoardPage[] = [];

  for (let pageNumber = 1; pageNumber <= pdfDocument.numPages; pageNumber += 1) {
    onProgress?.(pageNumber, pdfDocument.numPages);
    const page = await pdfDocument.getPage(pageNumber);
    const baseViewport = page.getViewport({ scale: 1 });
    const scale = Math.min(2.2, RENDER_WIDTH / baseViewport.width);
    const viewport = page.getViewport({ scale });
    const canvas = document.createElement("canvas");
    canvas.width = Math.ceil(viewport.width);
    canvas.height = Math.ceil(viewport.height);
    const context = canvas.getContext("2d", { alpha: false });
    if (!context) {
      throw new Error("当前设备无法创建幻灯片画布");
    }

    context.fillStyle = "#ffffff";
    context.fillRect(0, 0, canvas.width, canvas.height);
    await page.render({ canvas, canvasContext: context, viewport }).promise;
    const dataUrl = canvas.toDataURL("image/jpeg", 0.92);
    pages.push(
      await createImagePage({
        dataUrl,
        name: fileName,
        kind,
        pageNumber,
      }),
    );
    page.cleanup();
  }

  await pdfDocument.destroy();
  return pages;
};
