use serde::Serialize;
use std::{
    fs::{self, File, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
};
use tauri::{AppHandle, Manager};

const MAX_BOARD_BYTES: usize = 300 * 1024 * 1024;
const MAX_WELCOME_BYTES: usize = 120 * 1024 * 1024;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NativeStatePair {
    current: Option<String>,
    backup: Option<String>,
}

fn data_root(app: &AppHandle) -> Result<PathBuf, String> {
    app.path()
        .app_data_dir()
        .map(|path| path.join("state"))
        .map_err(|_| "无法定位本机数据目录".to_string())
}

fn read_optional(path: &Path) -> Option<String> {
    fs::read_to_string(path).ok()
}

fn load_pair(app: &AppHandle, name: &str) -> Result<NativeStatePair, String> {
    let root = data_root(app)?;
    Ok(NativeStatePair {
        current: read_optional(&root.join(format!("{name}.json"))),
        backup: read_optional(&root.join(format!("{name}.backup.json"))),
    })
}

fn validate_payload(payload: &str, max_bytes: usize) -> Result<(), String> {
    if payload.len() > max_bytes {
        return Err("本地数据超过安全保存上限".to_string());
    }
    let value: serde_json::Value =
        serde_json::from_str(payload).map_err(|_| "本地数据不是有效 JSON".to_string())?;
    if !value.is_object() {
        return Err("本地数据格式无效".to_string());
    }
    Ok(())
}

fn sync_file(path: &Path, payload: &[u8]) -> Result<(), String> {
    let mut file = OpenOptions::new()
        .create(true)
        .truncate(true)
        .write(true)
        .open(path)
        .map_err(|_| "无法创建本地临时文件".to_string())?;
    file.write_all(payload)
        .map_err(|_| "无法写入本地临时文件".to_string())?;
    file.sync_all()
        .map_err(|_| "无法刷新本地临时文件".to_string())?;
    Ok(())
}

fn atomic_write_with_backup(
    root: &Path,
    name: &str,
    payload: &str,
    max_bytes: usize,
) -> Result<(), String> {
    validate_payload(payload, max_bytes)?;
    fs::create_dir_all(root).map_err(|_| "无法创建本机数据目录".to_string())?;

    let current = root.join(format!("{name}.json"));
    let backup = root.join(format!("{name}.backup.json"));
    let temporary = root.join(format!("{name}.tmp"));
    sync_file(&temporary, payload.as_bytes())?;

    if backup.exists() {
        fs::remove_file(&backup).map_err(|_| "无法轮换本地恢复快照".to_string())?;
    }
    if current.exists() {
        fs::rename(&current, &backup).map_err(|_| "无法创建本地恢复快照".to_string())?;
    }

    if let Err(error) = fs::rename(&temporary, &current) {
        if backup.exists() && !current.exists() {
            let _ = fs::rename(&backup, &current);
        }
        return Err(format!("无法提交本地数据：{error}"));
    }

    if let Ok(directory) = File::open(root) {
        let _ = directory.sync_all();
    }
    Ok(())
}

#[tauri::command]
pub fn load_board_documents(app: AppHandle) -> Result<NativeStatePair, String> {
    load_pair(&app, "board")
}

#[tauri::command]
pub fn save_board_document(app: AppHandle, payload: String) -> Result<(), String> {
    let root = data_root(&app)?;
    atomic_write_with_backup(&root, "board", &payload, MAX_BOARD_BYTES)
}

#[tauri::command]
pub fn load_welcome_preferences(app: AppHandle) -> Result<NativeStatePair, String> {
    load_pair(&app, "welcome")
}

#[tauri::command]
pub fn save_welcome_preferences(app: AppHandle, payload: String) -> Result<(), String> {
    let root = data_root(&app)?;
    atomic_write_with_backup(&root, "welcome", &payload, MAX_WELCOME_BYTES)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rotates_previous_state_into_backup() {
        let root =
            std::env::temp_dir().join(format!("nanhara-board-storage-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);

        atomic_write_with_backup(&root, "board", r#"{"version":1}"#, 1024).unwrap();
        atomic_write_with_backup(&root, "board", r#"{"version":2}"#, 1024).unwrap();

        assert_eq!(
            fs::read_to_string(root.join("board.json")).unwrap(),
            r#"{"version":2}"#
        );
        assert_eq!(
            fs::read_to_string(root.join("board.backup.json")).unwrap(),
            r#"{"version":1}"#
        );

        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn rejects_non_object_payloads() {
        assert!(validate_payload("[]", 1024).is_err());
        assert!(validate_payload("not-json", 1024).is_err());
    }
}
