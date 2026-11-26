import { newBoardId } from "./board-model";

const MAX_WELCOME_IMAGE_BYTES = 15 * 1024 * 1024;

export type LoadedImage = {
  dataUrl: string;
  width: number;
  height: number;
};

export const readFileAsDataUrl = (file: Blob): Promise<string> =>
  new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result));
    reader.onerror = () => reject(reader.error ?? new Error("无法读取文件"));
    reader.readAsDataURL(file);
  });

export const inspectImage = (dataUrl: string): Promise<LoadedImage> =>
  new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () =>
      resolve({
        dataUrl,
        width: image.naturalWidth,
        height: image.naturalHeight,
      });
    image.onerror = () => reject(new Error("图片格式无法识别"));
    image.src = dataUrl;
  });

export const createThumbnail = async (
  image: LoadedImage,
  maxWidth = 240,
): Promise<string> => {
  const ratio = Math.min(1, maxWidth / image.width);
  const canvas = document.createElement("canvas");
  canvas.width = Math.max(1, Math.round(image.width * ratio));
  canvas.height = Math.max(1, Math.round(image.height * ratio));

  const context = canvas.getContext("2d");
  if (!context) {
    return image.dataUrl;
  }

  const bitmap = await inspectImage(image.dataUrl);
  const source = new Image();
  source.src = bitmap.dataUrl;
  await source.decode();
  context.drawImage(source, 0, 0, canvas.width, canvas.height);
  return canvas.toDataURL("image/jpeg", 0.74);
};

export const loadWelcomeAsset = async (file: File) => {
  if (file.size > MAX_WELCOME_IMAGE_BYTES) {
    throw new Error(`“${file.name}”超过 15 MB，请先压缩图片`);
  }
  const dataUrl = await readFileAsDataUrl(file);
  await inspectImage(dataUrl);
  return {
    id: newBoardId(),
    name: file.name,
    dataUrl,
  };
};
