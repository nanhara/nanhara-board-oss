import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  createBoardDocument,
  createBlankPage,
  type BoardDocument,
  type BoardPage,
} from "../lib/board-model";
import { loadBoardDocument, persistBoardDocument } from "../lib/persistence";

export type SaveState = "loading" | "saved" | "saving" | "error";

const SAVE_DELAY_MS = 420;
const SAVE_RETRY_MS = 5_000;

export const useBoardDocument = () => {
  const [document, setDocument] = useState<BoardDocument | null>(null);
  const [saveState, setSaveState] = useState<SaveState>("loading");
  const [recovered, setRecovered] = useState(false);
  const initialLoadComplete = useRef(false);

  useEffect(() => {
    let cancelled = false;
    void loadBoardDocument()
      .then((result) => {
        if (cancelled) return;
        setDocument(result.document);
        setRecovered(result.recovered);
        setSaveState("saved");
        initialLoadComplete.current = true;
      })
      .catch(() => {
        if (cancelled) return;
        setDocument(createBoardDocument());
        setSaveState("error");
        initialLoadComplete.current = true;
      });

    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    if (!document || !initialLoadComplete.current) return;

    setSaveState("saving");
    let cancelled = false;
    let timer = 0;
    const save = () => {
      void persistBoardDocument(document)
        .then(() => {
          if (!cancelled) setSaveState("saved");
        })
        .catch(() => {
          if (cancelled) return;
          setSaveState("error");
          timer = window.setTimeout(save, SAVE_RETRY_MS);
        });
    };
    timer = window.setTimeout(save, SAVE_DELAY_MS);

    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [document]);

  const mutate = useCallback((recipe: (current: BoardDocument) => BoardDocument) => {
    setDocument((current) => {
      if (!current) return current;
      const next = recipe(current);
      return { ...next, updatedAt: new Date().toISOString() };
    });
  }, []);

  const rename = useCallback(
    (title: string) => mutate((current) => ({ ...current, title: title.slice(0, 80) })),
    [mutate],
  );

  const updatePageScene = useCallback(
    (pageId: string, scene: string) =>
      mutate((current) => ({
        ...current,
        pages: current.pages.map((page) =>
          page.id === pageId ? { ...page, scene } : page,
        ),
      })),
    [mutate],
  );

  const selectPage = useCallback(
    (pageId: string) =>
      mutate((current) =>
        current.pages.some((page) => page.id === pageId)
          ? { ...current, activePageId: pageId }
          : current,
      ),
    [mutate],
  );

  const addBlank = useCallback(() => {
    mutate((current) => {
      const page = createBlankPage(current.pages.length + 1);
      return {
        ...current,
        activePageId: page.id,
        pages: [...current.pages, page],
      };
    });
  }, [mutate]);

  const addPages = useCallback(
    (pages: BoardPage[]) => {
      if (pages.length === 0) return;
      mutate((current) => ({
        ...current,
        activePageId: pages[0].id,
        pages: [...current.pages, ...pages],
      }));
    },
    [mutate],
  );

  const removePage = useCallback(
    (pageId: string) =>
      mutate((current) => {
        if (current.pages.length === 1) return current;
        const pageIndex = current.pages.findIndex((page) => page.id === pageId);
        const pages = current.pages.filter((page) => page.id !== pageId);
        const activePageId =
          current.activePageId === pageId
            ? pages[Math.min(Math.max(pageIndex, 0), pages.length - 1)].id
            : current.activePageId;
        return { ...current, pages, activePageId };
      }),
    [mutate],
  );

  const activePage = useMemo(
    () => document?.pages.find((page) => page.id === document.activePageId) ?? null,
    [document],
  );

  const movePage = useCallback(
    (direction: -1 | 1) => {
      if (!document) return;
      const currentIndex = document.pages.findIndex(
        (page) => page.id === document.activePageId,
      );
      const nextIndex = Math.min(
        Math.max(currentIndex + direction, 0),
        document.pages.length - 1,
      );
      if (nextIndex !== currentIndex) selectPage(document.pages[nextIndex].id);
    },
    [document, selectPage],
  );

  return {
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
  };
};
