import { useRef, useState } from "react";
import { ImagePlus, X } from "lucide-react";
import type { BoardPage, WelcomeContentMode, WelcomePreferences } from "../lib/board-model";
import { loadWelcomeAsset } from "../lib/image-utils";

export function WelcomeSettings({
  value,
  pages,
  activePageId,
  onChange,
  onClose,
}: {
  value: WelcomePreferences;
  pages: BoardPage[];
  activePageId: string;
  onChange: (value: WelcomePreferences) => Promise<void>;
  onClose: () => void;
}) {
  const [draft, setDraft] = useState(value);
  const [imageError, setImageError] = useState("");
  const [saveError, setSaveError] = useState("");
  const [saving, setSaving] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);

  const save = async () => {
    setSaving(true);
    setSaveError("");
    try {
      await onChange(draft);
      onClose();
    } catch {
      setSaveError("设置未保存，请检查磁盘空间后重试。");
    } finally {
      setSaving(false);
    }
  };

  const selectContentMode = (contentMode: WelcomeContentMode) => {
    setDraft((current) => ({
      ...current,
      contentMode,
      boardPageId:
        contentMode === "board-page"
          ? pages.some((page) => page.id === current.boardPageId)
            ? current.boardPageId
            : activePageId || pages[0]?.id || null
          : current.boardPageId,
    }));
  };

  return (
    <div className="dialog-backdrop" role="presentation" onPointerDown={onClose}>
      <section
        className="settings-dialog"
        role="dialog"
        aria-modal="true"
        aria-labelledby="welcome-settings-title"
        onPointerDown={(event) => event.stopPropagation()}
      >
        <header>
          <div>
            <span className="eyebrow">WELCOME MODE</span>
            <h2 id="welcome-settings-title">欢迎屏设置</h2>
          </div>
          <button type="button" className="icon-button" onClick={onClose} aria-label="关闭">
            <X aria-hidden="true" size={22} />
          </button>
        </header>

        <div className="content-source">
          <span>显示内容</span>
          <div className="content-source__options">
            {([
              ["board-page", "白板页面", "把排好的某一页直接作为欢迎屏"],
              ["images", "图片轮播", "循环播放导入到本机的欢迎图片"],
              ["builtin", "内置画面", "使用南荒默认欢迎设计"],
            ] as const).map(([mode, label, description]) => (
              <button
                key={mode}
                type="button"
                className={draft.contentMode === mode ? "is-active" : ""}
                onClick={() => selectContentMode(mode)}
              >
                <strong>{label}</strong>
                <small>{description}</small>
              </button>
            ))}
          </div>
        </div>

        <div className="settings-dialog__grid">
          {draft.contentMode === "board-page" ? (
            <label className="field field--wide">
              <span>选择白板页面</span>
              <select
                value={draft.boardPageId ?? ""}
                onChange={(event) =>
                  setDraft((current) => ({
                    ...current,
                    boardPageId: event.target.value || null,
                  }))
                }
              >
                {pages.map((page, index) => (
                  <option key={page.id} value={page.id}>
                    {String(index + 1).padStart(2, "0")} · {page.name}
                  </option>
                ))}
              </select>
              <small>进入欢迎模式时生成只读快照；修改该页后会自动显示新内容。</small>
            </label>
          ) : (
            <>
              <label className="field field--wide">
                <span>欢迎标题</span>
                <input
                  value={draft.headline}
                  onChange={(event) =>
                    setDraft((current) => ({ ...current, headline: event.target.value }))
                  }
                  maxLength={40}
                />
              </label>
              <label className="field field--wide">
                <span>副标题</span>
                <input
                  value={draft.subline}
                  onChange={(event) =>
                    setDraft((current) => ({ ...current, subline: event.target.value }))
                  }
                  maxLength={80}
                />
              </label>
            </>
          )}
          <label className="field">
            <span>闲置多久进入</span>
            <select
              value={draft.idleMinutes}
              onChange={(event) =>
                setDraft((current) => ({
                  ...current,
                  idleMinutes: Number(event.target.value),
                  enabled: Number(event.target.value) > 0,
                }))
              }
            >
              <option value={0}>不自动进入</option>
              <option value={1}>1 分钟</option>
              <option value={5}>5 分钟</option>
              <option value={10}>10 分钟</option>
              <option value={30}>30 分钟</option>
            </select>
          </label>
          {draft.contentMode === "images" ? (
            <label className="field">
              <span>图片切换时间</span>
              <select
                value={draft.slideSeconds}
                onChange={(event) =>
                  setDraft((current) => ({
                    ...current,
                    slideSeconds: Number(event.target.value),
                  }))
                }
              >
                <option value={6}>6 秒</option>
                <option value={12}>12 秒</option>
                <option value={20}>20 秒</option>
                <option value={30}>30 秒</option>
              </select>
            </label>
          ) : null}
        </div>

        {draft.contentMode === "images" ? (
          <div className="asset-library">
            <div className="asset-library__heading">
              <div>
                <strong>欢迎图片</strong>
                <span>图片会复制进应用的本地库，原 U 盘拔掉也能显示。</span>
              </div>
              <button type="button" className="secondary-button" onClick={() => inputRef.current?.click()}>
                <ImagePlus aria-hidden="true" size={19} />
                添加图片
              </button>
            </div>
            <input
              ref={inputRef}
              type="file"
              accept="image/png,image/jpeg,image/webp"
              multiple
              hidden
              onChange={(event) => {
                const files = Array.from(event.target.files ?? []);
                setImageError("");
                const existingBytes = draft.assets.reduce(
                  (total, asset) => total + Math.floor(asset.dataUrl.length * 0.75),
                  0,
                );
                const incomingBytes = files.reduce((total, file) => total + file.size, 0);
                if (existingBytes + incomingBytes > 60 * 1024 * 1024) {
                  setImageError("欢迎图片库最多 60 MB，请先移除或压缩部分图片");
                  event.target.value = "";
                  return;
                }
                void Promise.all(files.map(loadWelcomeAsset))
                  .then((assets) =>
                    setDraft((current) => ({
                      ...current,
                      assets: [...current.assets, ...assets],
                    })),
                  )
                  .catch((error: unknown) =>
                    setImageError(error instanceof Error ? error.message : "图片读取失败"),
                  );
                event.target.value = "";
              }}
            />
            {draft.assets.length > 0 ? (
              <div className="asset-library__items">
                {draft.assets.map((asset) => (
                  <div className="asset-card" key={asset.id}>
                    <img src={asset.dataUrl} alt={asset.name} />
                    <span>{asset.name}</span>
                    <button
                      type="button"
                      onClick={() =>
                        setDraft((current) => ({
                          ...current,
                          assets: current.assets.filter((item) => item.id !== asset.id),
                        }))
                      }
                      aria-label={`移除${asset.name}`}
                    >
                      <X aria-hidden="true" size={16} />
                    </button>
                  </div>
                ))}
              </div>
            ) : (
              <button type="button" className="asset-library__empty" onClick={() => inputRef.current?.click()}>
                <ImagePlus aria-hidden="true" size={26} />
                <strong>添加一张客户欢迎图</strong>
                <span>没有图片时会使用内置的南荒欢迎页</span>
              </button>
            )}
            {imageError ? <p className="field-error">{imageError}</p> : null}
            </div>
        ) : draft.contentMode === "board-page" ? (
          <div className="board-page-note">
            <strong>页面就是欢迎屏</strong>
            <span>不会显示编辑工具，也不会让来宾误画；所选页面被删除或内容损坏时，自动回退到内置画面。</span>
          </div>
        ) : (
          <div className="board-page-note">
            <strong>始终可用的离线兜底</strong>
            <span>无需准备素材，适合第一次安装和自定义内容暂时不可用时展示。</span>
          </div>
        )}

        {saveError ? <p className="field-error settings-save-error">{saveError}</p> : null}

        <footer>
          <span>首版所有欢迎屏功能免费使用</span>
          <div>
            <button type="button" className="secondary-button" onClick={onClose}>取消</button>
            <button
              type="button"
              className="primary-button"
              onClick={() => void save()}
              disabled={saving}
            >
              {saving ? "正在写入本机…" : "保存设置"}
            </button>
          </div>
        </footer>
      </section>
    </div>
  );
}
