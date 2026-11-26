import type { BoardPage } from "./board-model";
import { readFileAsDataUrl } from "./image-utils";
import { createImagePage } from "./scene-factory";

export type ImportProgress = {
  label: string;
  current?: number;
  total?: number;
};

type ConvertedPresentation = {
  bytes: number[];
  engine: "powerpoint" | "pdf";
  sourceName: string;
};

const isDesktopApp = () => Boolean(window.__TAURI_INTERNALS__);

const importPdfFile = async (
  file: File,
  onProgress?: (progress: ImportProgress) => void,
): Promise<BoardPage[]> => {
  const { importPdfBytes } = await import("./pdf-import");
  return importPdfBytes({
    bytes: new Uint8Array(await file.arrayBuffer()),
    fileName: file.name,
    onProgress: (current, total) =>
      onProgress?.({ label: "正在生成幻灯片", current, total }),
  });
};

const importImageFiles = async (files: File[]): Promise<BoardPage[]> =>
  Promise.all(
    files.map(async (file) =>
      createImagePage({
        dataUrl: await readFileAsDataUrl(file),
        name: file.name,
        kind: "image",
      }),
    ),
  );

export const importBrowserFiles = async (
  files: File[],
  onProgress?: (progress: ImportProgress) => void,
): Promise<BoardPage[]> => {
  const pdf = files.find((file) => file.name.toLowerCase().endsWith(".pdf"));
  if (pdf) {
    return importPdfFile(pdf, onProgress);
  }

  const presentation = files.find((file) => /\.pptx?$/i.test(file.name));
  if (presentation) {
    throw new Error(
      isDesktopApp()
        ? "请使用应用内“导入演示文稿”按钮选择 PPT"
        : "PPT/PPTX 转换需要 Windows 桌面版；浏览器预览可先导出为 PDF",
    );
  }

  const images = files.filter((file) => file.type.startsWith("image/"));
  if (images.length > 0) {
    return importImageFiles(images);
  }

  throw new Error("请选择 PPT、PPTX、PDF 或图片文件");
};

export const importPresentationFromDesktop = async (
  onProgress?: (progress: ImportProgress) => void,
): Promise<BoardPage[] | null> => {
  if (!isDesktopApp()) {
    return null;
  }

  const [{ open }, { invoke }] = await Promise.all([
    import("@tauri-apps/plugin-dialog"),
    import("@tauri-apps/api/core"),
  ]);
  const selected = await open({
    multiple: false,
    directory: false,
    filters: [
      { name: "演示文稿", extensions: ["ppt", "pptx", "pdf"] },
    ],
  });

  if (!selected) {
    return [];
  }

  onProgress?.({ label: "正在调用本机演示转换器" });
  const converted = await invoke<ConvertedPresentation>("convert_presentation", {
    sourcePath: selected,
  });
  const { importPdfBytes } = await import("./pdf-import");
  return importPdfBytes({
    bytes: Uint8Array.from(converted.bytes),
    fileName: converted.sourceName,
    kind: converted.engine === "powerpoint" ? "powerpoint" : "pdf",
    onProgress: (current, total) =>
      onProgress?.({
        label: `正在生成幻灯片 · ${converted.engine === "powerpoint" ? "PowerPoint" : "PDF"}`,
        current,
        total,
      }),
  });
};
