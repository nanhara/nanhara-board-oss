mod presentation;
mod storage;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            presentation::convert_presentation,
            storage::load_board_documents,
            storage::save_board_document,
            storage::load_welcome_preferences,
            storage::save_welcome_preferences,
        ])
        .run(tauri::generate_context!())
        .expect("failed to run Nanhara Board");
}
