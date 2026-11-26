use serde::Serialize;
use std::{
    fs,
    path::{Path, PathBuf},
};
use tauri::{AppHandle, Manager};

const MAX_PRESENTATION_BYTES: u64 = 250 * 1024 * 1024;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ConvertedPresentation {
    bytes: Vec<u8>,
    engine: String,
    source_name: String,
}

fn validated_source(source_path: &str) -> Result<(PathBuf, String, String), String> {
    let source = fs::canonicalize(source_path).map_err(|_| "无法读取所选文件".to_string())?;
    let metadata = fs::metadata(&source).map_err(|_| "无法读取所选文件".to_string())?;
    if !metadata.is_file() || metadata.len() > MAX_PRESENTATION_BYTES {
        return Err("演示文稿无效或超过 250 MB".to_string());
    }
    let extension = source
        .extension()
        .and_then(|value| value.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();
    if !matches!(extension.as_str(), "ppt" | "pptx" | "pdf") {
        return Err("请选择 PPT、PPTX 或 PDF 文件".to_string());
    }
    let source_name = source
        .file_name()
        .and_then(|value| value.to_str())
        .unwrap_or("演示文稿")
        .to_string();
    Ok((source, source_name, extension))
}

#[cfg(target_os = "windows")]
fn convert_powerpoint(source: &Path, cache_dir: &Path) -> Result<Vec<u8>, String> {
    use std::{
        os::windows::process::CommandExt,
        process::{Command, Stdio},
        thread,
        time::{Duration, Instant, SystemTime, UNIX_EPOCH},
    };

    fs::create_dir_all(cache_dir).map_err(|_| "无法创建演示转换缓存".to_string())?;
    let script_path = cache_dir.join("powerpoint-export-v1.ps1");
    fs::write(
        &script_path,
        include_str!("../scripts/powerpoint-export.ps1"),
    )
    .map_err(|_| "无法准备 PowerPoint 转换器".to_string())?;
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis();
    let output_path = cache_dir.join(format!("presentation-{nonce}.pdf"));

    let mut command = Command::new("powershell.exe");
    command
        .creation_flags(0x0800_0000)
        .args([
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
        ])
        .arg(&script_path)
        .arg("-InputPath")
        .arg(source)
        .arg("-OutputPath")
        .arg(&output_path)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    let mut child = command
        .spawn()
        .map_err(|_| "无法启动 PowerPoint 转换器".to_string())?;

    let started = Instant::now();
    let process_result = loop {
        match child.try_wait() {
            Ok(Some(status)) => break status.success(),
            Ok(None) if started.elapsed() < Duration::from_secs(120) => {
                thread::sleep(Duration::from_millis(100));
            }
            Ok(None) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err("PowerPoint 转换超过 120 秒，已安全取消".to_string());
            }
            Err(_) => return Err("无法确认 PowerPoint 转换状态".to_string()),
        }
    };

    if !process_result || !output_path.exists() {
        return Err("PowerPoint 转换失败；请确认已安装 PowerPoint，文件未加密，并重试".to_string());
    }

    let bytes = fs::read(&output_path).map_err(|_| "无法读取转换后的 PDF".to_string())?;
    let _ = fs::remove_file(output_path);
    Ok(bytes)
}

#[cfg(not(target_os = "windows"))]
fn convert_powerpoint(_source: &Path, _cache_dir: &Path) -> Result<Vec<u8>, String> {
    Err("PPT/PPTX 转换只在 Windows 桌面版中启用；当前可先导入 PDF".to_string())
}

#[tauri::command]
pub async fn convert_presentation(
    app: AppHandle,
    source_path: String,
) -> Result<ConvertedPresentation, String> {
    let (source, source_name, extension) = validated_source(&source_path)?;
    if extension == "pdf" {
        return Ok(ConvertedPresentation {
            bytes: fs::read(source).map_err(|_| "无法读取 PDF".to_string())?,
            engine: "pdf".to_string(),
            source_name,
        });
    }

    let cache_dir = app
        .path()
        .app_cache_dir()
        .map_err(|_| "无法定位演示转换缓存".to_string())?
        .join("presentation-import");
    let bytes =
        tauri::async_runtime::spawn_blocking(move || convert_powerpoint(&source, &cache_dir))
            .await
            .map_err(|_| "PowerPoint 转换进程异常结束".to_string())??;

    Ok(ConvertedPresentation {
        bytes,
        engine: "powerpoint".to_string(),
        source_name,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_unknown_extensions() {
        let root = std::env::temp_dir().join(format!(
            "nanhara-board-presentation-{}.txt",
            std::process::id()
        ));
        fs::write(&root, "test").unwrap();
        assert!(validated_source(root.to_str().unwrap()).is_err());
        let _ = fs::remove_file(root);
    }
}
