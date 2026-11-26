import { ChevronLeft, ChevronRight, Plus, Trash2 } from "lucide-react";
import type { BoardDocument } from "../lib/board-model";

export function PageRail({
  document,
  onSelect,
  onAdd,
  onRemove,
  onMove,
}: {
  document: BoardDocument;
  onSelect: (pageId: string) => void;
  onAdd: () => void;
  onRemove: (pageId: string) => void;
  onMove: (direction: -1 | 1) => void;
}) {
  return (
    <aside className="page-rail" aria-label="白板页面">
      <div className="page-rail__heading">
        <span>页面</span>
        <strong>{document.pages.length}</strong>
      </div>
      <div className="page-rail__list">
        {document.pages.map((page, index) => {
          const active = page.id === document.activePageId;
          return (
            <div className="page-item-wrap" key={page.id}>
              <button
                type="button"
                className={`page-item${active ? " is-active" : ""}`}
                onClick={() => onSelect(page.id)}
                aria-current={active ? "page" : undefined}
              >
                <span className="page-item__number">{String(index + 1).padStart(2, "0")}</span>
                <span className="page-item__preview">
                  {page.thumbnail ? (
                    <img src={page.thumbnail} alt="" />
                  ) : (
                    <span className="page-item__blank" />
                  )}
                </span>
                <span className="page-item__name">{page.name}</span>
              </button>
              {active && document.pages.length > 1 ? (
                <button
                  type="button"
                  className="page-item__delete"
                  onClick={() => onRemove(page.id)}
                  aria-label={`删除${page.name}`}
                >
                  <Trash2 aria-hidden="true" size={17} />
                </button>
              ) : null}
            </div>
          );
        })}
      </div>
      <button type="button" className="page-rail__add" onClick={onAdd}>
        <Plus aria-hidden="true" size={21} />
        新白板
      </button>
      <div className="page-rail__nav">
        <button type="button" onClick={() => onMove(-1)} aria-label="上一页">
          <ChevronLeft aria-hidden="true" size={22} />
        </button>
        <button type="button" onClick={() => onMove(1)} aria-label="下一页">
          <ChevronRight aria-hidden="true" size={22} />
        </button>
      </div>
    </aside>
  );
}
