import { useEffect, useMemo, useState } from "react";
import { ArrowUpRight } from "lucide-react";
import appLogo from "../assets/app-logo.png";
import type { BoardPage, WelcomePreferences } from "../lib/board-model";
import { renderBoardPagePreview } from "../lib/board-page-preview";

const formatClock = (date: Date) =>
  new Intl.DateTimeFormat("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  }).format(date);

const formatDate = (date: Date) =>
  new Intl.DateTimeFormat("zh-CN", {
    month: "long",
    day: "numeric",
    weekday: "long",
  }).format(date);

export function WelcomeScreen({
  preferences,
  boardPage,
  onExit,
}: {
  preferences: WelcomePreferences;
  boardPage?: BoardPage;
  onExit: () => void;
}) {
  const [assetIndex, setAssetIndex] = useState(0);
  const [now, setNow] = useState(() => new Date());
  const [exitArmed, setExitArmed] = useState(false);
  const [boardPreviewUrl, setBoardPreviewUrl] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    let generatedUrl: string | null = null;
    setBoardPreviewUrl(null);

    if (preferences.contentMode === "board-page" && boardPage) {
      void renderBoardPagePreview(boardPage)
        .then((url) => {
          generatedUrl = url;
          if (!cancelled) setBoardPreviewUrl(url);
        })
        .catch(() => {
          if (!cancelled) setBoardPreviewUrl(null);
        });
    }

    return () => {
      cancelled = true;
      if (generatedUrl) URL.revokeObjectURL(generatedUrl);
    };
  }, [boardPage, preferences.contentMode]);

  useEffect(() => {
    // Native fullscreen transitions can replay the opening pointer gesture after
    // the React tree has changed. Keep the display inert until that gesture is
    // certainly finished, especially on slower touch-wall PCs.
    const timer = window.setTimeout(() => setExitArmed(true), 1_800);
    return () => window.clearTimeout(timer);
  }, []);

  useEffect(() => {
    if (!exitArmed) return;
    const exitOnKey = (event: KeyboardEvent) => {
      event.preventDefault();
      event.stopImmediatePropagation();
      onExit();
    };
    window.addEventListener("keydown", exitOnKey, {
      once: true,
      capture: true,
    });
    return () => window.removeEventListener("keydown", exitOnKey, true);
  }, [exitArmed, onExit]);

  useEffect(() => {
    const clockTimer = window.setInterval(() => setNow(new Date()), 30_000);
    return () => window.clearInterval(clockTimer);
  }, []);

  useEffect(() => {
    if (
      preferences.contentMode !== "images" ||
      preferences.assets.length <= 1
    ) {
      return;
    }
    const timer = window.setInterval(
      () => setAssetIndex((current) => (current + 1) % preferences.assets.length),
      preferences.slideSeconds * 1000,
    );
    return () => window.clearInterval(timer);
  }, [preferences.assets.length, preferences.contentMode, preferences.slideSeconds]);

  const activeAsset = useMemo(
    () =>
      preferences.contentMode === "images"
        ? preferences.assets[assetIndex % Math.max(preferences.assets.length, 1)]
        : undefined,
    [assetIndex, preferences.assets, preferences.contentMode],
  );
  const showingBoardPage = Boolean(boardPreviewUrl);

  return (
    <section
      className={`welcome-screen${showingBoardPage ? " welcome-screen--board-page" : ""}`}
      onPointerDown={() => {
        if (exitArmed) onExit();
      }}
    >
      {showingBoardPage ? (
        <div className="welcome-screen__board-page">
          <img src={boardPreviewUrl ?? ""} alt={boardPage?.name ?? "欢迎白板页"} />
        </div>
      ) : activeAsset ? (
        <img
          key={activeAsset.id}
          className="welcome-screen__image"
          src={activeAsset.dataUrl}
          alt=""
        />
      ) : (
        <div className="welcome-screen__default-art" aria-hidden="true">
          <span className="orbit orbit--one" />
          <span className="orbit orbit--two" />
          <span className="seal">南荒</span>
        </div>
      )}
      {showingBoardPage ? (
        <div className="welcome-screen__page-meta">
          <span>{boardPage?.name}</span>
          <strong>{formatClock(now)}</strong>
        </div>
      ) : (
        <>
          <div className="welcome-screen__veil" />
          <header className="welcome-screen__topline">
            <div className="brand-lockup brand-lockup--light">
              <img className="brand-lockup__mark" src={appLogo} alt="" />
              <span>
                <strong>南荒白板</strong>
                <small>NANHARA BOARD</small>
              </span>
            </div>
            <div className="welcome-screen__clock">
              <strong>{formatClock(now)}</strong>
              <span>{formatDate(now)}</span>
            </div>
          </header>
          <div className="welcome-screen__message">
            <span className="welcome-screen__eyebrow">WELCOME · 欢迎模式</span>
            <h1>{preferences.headline || "欢迎莅临"}</h1>
            <p>{preferences.subline || "无锡南荒科技有限公司"}</p>
          </div>
        </>
      )}
      <button
        type="button"
        className={`welcome-screen__exit${showingBoardPage ? " welcome-screen__exit--on-page" : ""}`}
        onClick={() => {
          if (exitArmed) onExit();
        }}
      >
        触摸任意处开始
        <ArrowUpRight aria-hidden="true" size={21} />
      </button>
      {preferences.contentMode === "images" && preferences.assets.length > 1 ? (
        <div className="welcome-screen__dots" aria-label="欢迎图片进度">
          {preferences.assets.map((asset, index) => (
            <span key={asset.id} className={index === assetIndex ? "is-active" : ""} />
          ))}
        </div>
      ) : null}
    </section>
  );
}
