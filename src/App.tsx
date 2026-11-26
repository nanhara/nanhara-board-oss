import {
  lazy,
  Suspense,
  useCallback,
  useEffect,
  useRef,
  useState,
} from "react";
import {
  FileUp,
  Images,
  MonitorUp,
  Plus,
  Settings2,
  WifiOff,
  X,
} from "lucide-react";
import appLogo from "./assets/app-logo.png";
import { PageRail } from "./components/PageRail";
import { WelcomeScreen } from "./components/WelcomeScreen";
import { WelcomeSettings } from "./components/WelcomeSettings";
import { useBoardDocument } from "./hooks/useBoardDocument";
import { useIdleWelcome } from "./hooks/useIdleWelcome";
import { useWelcomePreferences } from "./hooks/useWelcomePreferences";
import {
  importBrowserFiles,
  importPresentationFromDesktop,
  type ImportProgress,
} from "./lib/presentation-import";
import {
  enterWelcomeWindowMode,
  leaveWelcomeWindowMode,
} from "./lib/window-mode";

const BoardCanvas = lazy(() => import("./components/BoardCanvas"));

const saveLabels = {
  loading: "正在恢复",
  saving: "正在保存",
  saved: "已离线保存",
  error: "保存异常",
} as const;

export default function App() {
  const {
    document,
    activePage,
    saveState,
    recovered,
    rename,
    updatePageScene,
    selectPage,
    addBlank,
    addPages,
    removePage,
    movePage,
  } = useBoardDocument();
  const { preferences, updatePreferences, persistenceError } =
    useWelcomePreferences();
  const [showWelcome, setShowWelcome] = useState(false);
  const [showWelcomeSettings, setShowWelcomeSettings] = useState(false);
  const [dismissedRecovery, setDismissedRecovery] = useState(false);
  const [importProgress, setImportProgress] = useState<ImportProgress | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const enterWelcome = useCallback(() => {
    setShowWelcome(true);
    void enterWelcomeWindowMode();
  }, []);

  const exitWelcome = useCallback(() => {
    setShowWelcome(false);
    void leaveWelcomeWindowMode();
  }, []);

  useIdleWelcome({
    enabled: preferences?.enabled ?? false,
    idleMinutes: preferences?.idleMinutes ?? 0,
    paused: showWelcome || showWelcomeSettings || Boolean(importProgress),
    onIdle: enterWelcome,
  });

  useEffect(() => {
    if (!notice) return;
    const timer = window.setTimeout(() => setNotice(null), 5000);
    return () => window.clearTimeout(timer);
  }, [notice]);

  useEffect(() => {
    if (persistenceError) setNotice(persistenceError);
  }, [persistenceError]);

  const addImportedPages = useCallback(
    (pages: Awaited<ReturnType<typeof importBrowserFiles>>) => {
      addPages(pages);
      setNotice(`已导入 ${pages.length} 页，原文件未被修改`);
    },
    [addPages],
  );

  const openPresentation = useCallback(async () => {
    try {
      setImportProgress({ label: "请选择 PPT、PPTX 或 PDF" });
      const pages = await importPresentationFromDesktop(setImportProgress);
      if (pages === null) {
        setImportProgress(null);
        fileInputRef.current?.click();
        return;
      }
      if (pages.length > 0) addImportedPages(pages);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "演示文稿导入失败");
    } finally {
      setImportProgress(null);
    }
  }, [addImportedPages]);

  if (!document || !activePage || !preferences) {
    return (
      <main className="launch-screen">
        <img className="launch-screen__mark" src={appLogo} alt="南荒白板" />
        <span>南荒工具坊 · NH–004</span>
        <h1>南荒白板</h1>
        <p>正在恢复上一次的本地白板…</p>
        <div className="launch-screen__line" />
      </main>
    );
  }

  if (showWelcome) {
    return (
      <WelcomeScreen
        preferences={preferences}
        boardPage={document.pages.find((page) => page.id === preferences.boardPageId)}
        onExit={exitWelcome}
      />
    );
  }

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand-lockup">
          <img className="brand-lockup__mark" src={appLogo} alt="" />
          <span>
            <strong>南荒白板</strong>
            <small>NH–004 · FREE PREVIEW</small>
          </span>
        </div>

        <div className="document-title">
          <input
            aria-label="白板名称"
            value={document.title}
            onChange={(event) => rename(event.target.value)}
            onFocus={(event) => event.currentTarget.select()}
          />
          <span className={`save-indicator save-indicator--${saveState}`}>
            <WifiOff aria-hidden="true" size={15} />
            {saveLabels[saveState]}
          </span>
        </div>

        <nav className="topbar__actions" aria-label="白板操作">
          <button type="button" className="toolbar-button" onClick={addBlank}>
            <Plus aria-hidden="true" size={20} />
            新白板
          </button>
          <button type="button" className="toolbar-button toolbar-button--accent" onClick={openPresentation}>
            <FileUp aria-hidden="true" size={20} />
            导入 PPT / PDF
          </button>
          <button type="button" className="toolbar-button" onClick={enterWelcome}>
            <MonitorUp aria-hidden="true" size={20} />
            欢迎屏
          </button>
          <button
            type="button"
            className="icon-button icon-button--topbar"
            onClick={() => setShowWelcomeSettings(true)}
            aria-label="欢迎屏设置"
          >
            <Settings2 aria-hidden="true" size={21} />
          </button>
        </nav>
      </header>

      <div className="workspace">
        <PageRail
          document={document}
          onSelect={selectPage}
          onAdd={addBlank}
          onRemove={removePage}
          onMove={movePage}
        />
        <section className="canvas-stage">
          <div className="canvas-stage__meta">
            <span>{activePage.source.kind === "blank" ? "自由白板" : "演示批注"}</span>
            <strong>{activePage.name}</strong>
          </div>
          <Suspense
            fallback={
              <div className="canvas-loading">
                <span className="canvas-loading__glyph">墨</span>
                <p>正在准备画布</p>
              </div>
            }
          >
            <BoardCanvas
              key={activePage.id}
              pageId={activePage.id}
              scene={activePage.scene}
              onSceneChange={updatePageScene}
            />
          </Suspense>
        </section>
      </div>

      <input
        ref={fileInputRef}
        type="file"
        accept=".pdf,.ppt,.pptx,image/png,image/jpeg,image/webp"
        multiple
        hidden
        onChange={(event) => {
          const files = Array.from(event.target.files ?? []);
          if (files.length === 0) return;
          setImportProgress({ label: "正在读取文件" });
          void importBrowserFiles(files, setImportProgress)
            .then(addImportedPages)
            .catch((error: unknown) =>
              setNotice(error instanceof Error ? error.message : "文件导入失败"),
            )
            .finally(() => setImportProgress(null));
          event.target.value = "";
        }}
      />

      {showWelcomeSettings ? (
        <WelcomeSettings
          value={preferences}
          pages={document.pages}
          activePageId={document.activePageId}
          onChange={updatePreferences}
          onClose={() => setShowWelcomeSettings(false)}
        />
      ) : null}

      {importProgress ? (
        <div className="progress-overlay" role="status" aria-live="polite">
          <div className="progress-card">
            <Images aria-hidden="true" size={28} />
            <span className="eyebrow">LOCAL IMPORT</span>
            <h2>{importProgress.label}</h2>
            {importProgress.total ? (
              <div className="progress-card__track">
                <span
                  style={{
                    width: `${Math.round(((importProgress.current ?? 0) / importProgress.total) * 100)}%`,
                  }}
                />
              </div>
            ) : (
              <div className="progress-card__pulse" />
            )}
            <p>转换和批注数据都留在这台电脑上。</p>
          </div>
        </div>
      ) : null}

      {recovered && !dismissedRecovery ? (
        <div className="recovery-banner">
          已从本地恢复快照还原白板
          <button
            type="button"
            onClick={() => setDismissedRecovery(true)}
            aria-label="关闭提示"
          >
            <X aria-hidden="true" size={17} />
          </button>
        </div>
      ) : null}

      {notice ? (
        <div className="toast" role="status">
          {notice}
          <button type="button" onClick={() => setNotice(null)} aria-label="关闭提示">
            <X aria-hidden="true" size={17} />
          </button>
        </div>
      ) : null}
    </main>
  );
}
